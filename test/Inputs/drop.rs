pub struct NeedsDrop(pub i32);

impl Drop for NeedsDrop {
    fn drop(&mut self) {}
}

pub fn drop_local(value: i32) {
    let _local = NeedsDrop(value);
}
